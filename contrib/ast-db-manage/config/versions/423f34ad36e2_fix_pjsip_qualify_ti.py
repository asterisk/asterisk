"""fix pjsip qualify timeout

Revision ID: 423f34ad36e2
Revises: 136885b81223
Create Date: 2016-01-13 21:49:21.557734

"""

# revision identifiers, used by Alembic.
revision = '423f34ad36e2'
down_revision = '136885b81223'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.alter_column('ps_aors', 'qualify_timeout', type_=sa.Float)
    op.alter_column('ps_contacts', 'qualify_timeout', type_=sa.Float)

def downgrade():
    op.alter_column('ps_aors', 'qualify_timeout', type_=sa.Integer)
    op.alter_column('ps_contacts', 'qualify_timeout', type_=sa.Integer)
