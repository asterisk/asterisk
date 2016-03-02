"""create accountcode

Revision ID: 1d50859ed02e
Revises: 51f8cb66540e
Create Date: 2014-07-07 21:07:01.661783

"""

# revision identifiers, used by Alembic.
revision = '1d50859ed02e'
down_revision = '51f8cb66540e'

from alembic import op
import sqlalchemy as sa

def upgrade():
    op.add_column('ps_endpoints', sa.Column('accountcode', sa.String(20)))

def downgrade():
    with op.batch_alter_table('ps_endpoints') as batch_op:
        batch_op.drop_column('accountcode')
