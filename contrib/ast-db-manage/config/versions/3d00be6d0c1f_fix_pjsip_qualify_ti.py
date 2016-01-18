"""fix pjsip qualify timeout

Revision ID: 3d00be6d0c1f
Revises: 26d7f3bf0fa5
Create Date: 2016-01-16 19:48:31.730429

"""

# revision identifiers, used by Alembic.
revision = '3d00be6d0c1f'
down_revision = '26d7f3bf0fa5'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.alter_column('ps_aors', 'qualify_timeout', type_=sa.Decimal)
    op.alter_column('ps_contacts', 'qualify_timeout', type_=sa.Decimal)
    pass


def downgrade():
    op.alter_column('ps_aors', 'qualify_timeout', type_=sa.Integer)
    op.alter_column('ps_contacts', 'qualify_timeout', type_=sa.Integer)
    pass

