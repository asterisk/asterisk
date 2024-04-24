"""Add peer_supported to 100rel

Revision ID: 539f68bede2c
Revises: 9f3692b1654b
Create Date: 2022-08-10 09:36:16.576049

"""

# revision identifiers, used by Alembic.
revision = '539f68bede2c'
down_revision = '9f3692b1654b'

from alembic import op
from sqlalchemy.dialects.postgresql import ENUM
import sqlalchemy as sa


OLD_ENUM = ['no', 'required', 'yes']
NEW_ENUM = ['no', 'required', 'peer_supported', 'yes']

old_type = sa.Enum(*OLD_ENUM, name='pjsip_100rel_values')
new_type = sa.Enum(*NEW_ENUM, name='pjsip_100rel_values_v2')

def upgrade():
    context = op.get_context()

    # Upgrading to this revision WILL clear your directmedia values.
    if context.bind.dialect.name != 'postgresql':
        op.alter_column('ps_endpoints', '100rel',
                        type_=new_type,
                        existing_type=old_type)
    else:
        enum = ENUM(*NEW_ENUM, name='pjsip_100rel_values_v2')
        enum.create(op.get_bind(), checkfirst=False)

        op.execute('ALTER TABLE ps_endpoints ALTER COLUMN "100rel" TYPE'
                   ' pjsip_100rel_values_v2 USING'
                   ' "100rel"::text::pjsip_100rel_values_v2')

        ENUM(name="pjsip_100rel_values").drop(op.get_bind(), checkfirst=False)

def downgrade():
    context = op.get_context()

    if context.bind.dialect.name != 'postgresql':
        op.alter_column('ps_endpoints', '100rel',
                        type_=old_type,
                        existing_type=new_type)
    else:
        enum = ENUM(*OLD_ENUM, name='pjsip_100rel_values')
        enum.create(op.get_bind(), checkfirst=False)

        op.execute('ALTER TABLE ps_endpoints ALTER COLUMN "100rel" TYPE'
                   ' pjsip_100rel_values USING'
                   ' "100rel"::text::pjsip_100rel_values')

        ENUM(name="pjsip_100rel_values_v2").drop(op.get_bind(), checkfirst=False)
